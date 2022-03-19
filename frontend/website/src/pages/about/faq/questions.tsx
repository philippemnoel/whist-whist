import URLs from "@app/shared/constants/urls"

/* This example requires Tailwind CSS v2.0+ */
const faqs = [
  {
    id: 1,
    question: "How does Whist work?",
    answer: (
      <div>
        Whist is like remote desktop, but with a much greater emphasis on
        performance and user experience. You can read more about our technology{" "}
        <a
          href="/technology#top"
          className="text-gray-300 hover:text-blue-light"
        >
          here
        </a>
        .
      </div>
    ),
  },
  {
    id: 2,
    question: "Can I trust Whist with my data?",
    answer: (
      <div>
        We encrypt and protect every piece of data that you input through Whist
        so that not even our engineers can see your personal browsing data. You
        can read more about how we handle privacy and security{" "}
        <a href="/security#top" className="text-gray-300 hover:text-blue-light">
          here
        </a>
        .
      </div>
    ),
  },
  {
    id: 3,
    question: "What's the story behind Whist?",
    answer:
      "Whist was founded in 2019 out of Harvard University. Today, Whist is based in New York City and recruits the best systems engineers to push the frontiers of browser streaming.",
  },
  {
    id: 4,
    question: `How is Whist different than "X cloud streaming company?"`,
    answer:
      "Because we care only about streaming Chrome, we aim to create the user experience of running a browser on native hardware.",
  },
  {
    id: 5,
    question: "Will there be latency?",
    answer: (
      <div>
        Running applications in the cloud means that there will always be some
        latency, but there is a certain point at which latency becomes
        unnoticeable. To see if Whist can perform well for you, please review
        Whist&lsquo;s download requirements{" "}
        <a
          href="/download#requirements"
          className="text-gray-300 hover:text-blue-light"
        >
          here
        </a>
        .
      </div>
    ),
  },
  {
    id: 6,
    question: "Is Whist hiring?",
    answer: (
      <div>
        Yes! You can see our job postings{" "}
        <a
          href={URLs.NOTION_CAREERS}
          className="text-gray-300 hover:text-blue-light"
          target="_blank"
          rel="noreferrer"
        >
          here
        </a>
        .
      </div>
    ),
  },
  {
    id: 7,
    question: "How does Whist make money?",
    answer:
      "A Whist subscription is $9/mo. We are not an advertising business and will never sell your data.",
  },
]

const Questions = () => {
  return (
    <div className="bg-gray-900 my-16">
      <div className="max-w-7xl mx-auto py-16 px-4 sm:py-24 sm:px-6 lg:px-8">
        <div className="lg:max-w-2xl lg:mx-auto lg:text-center">
          <h2 className="text-3xl text-gray-300 sm:text-4xl">
            Frequently asked questions
          </h2>
          <p className="mt-4 text-gray-500">
            Have a question about Whist? You might find some answers here.
          </p>
        </div>
        <div className="mt-20">
          <dl className="space-y-10 lg:space-y-0 lg:grid lg:grid-cols-2 lg:gap-x-8 lg:gap-y-10">
            {faqs.map((faq) => (
              <div key={faq.id}>
                <dt className="font-semibold text-gray-300">{faq.question}</dt>
                <dd className="mt-3 text-gray-500">{faq.answer}</dd>
              </div>
            ))}
          </dl>
        </div>
      </div>
    </div>
  )
}

export default Questions
